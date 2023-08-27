#pragma once

#include "../search.hpp"
#include "../../board/TranspositionTable.hpp"

template<class Color>
MoveScore Search::nega_max(
    Board& b,
    Context& ctx,
    U16 depth,
    I16 alpha,
    I16 beta
) {
    constexpr bool turn = std::is_same<Color, White>::value;
    const I16 og_alpha = alpha;
    nodes++;
    node_depth_hist[depth]++;

    if (DrawTable::is_draw()) {
        leaves++;
        return { Move(), 0 };
    }

    // Quiescence: at nega_max leaf.

    if (depth == 0) {
        leaves++;
        I16 score = quiesce<Color>(b, ctx, alpha, beta);
        return { Move(), score };
    }

    // TT-lookup: to adjust bounds and get priority move.

    auto [ tt_hit, tt_cell ] = TranspositionTable::get_cell(ctx.hash, depth);
    if (tt_hit & (tt_cell->get_depth() >= depth)) {
        switch (tt_cell->node_type) {
            case TranspositionTable::NodeType::EXACT: {
                leaves++;
                return { tt_cell->move, tt_cell->score };
            }
            case TranspositionTable::NodeType::LOWER: alpha = std::max(alpha, tt_cell->score);
            case TranspositionTable::NodeType::UPPER: beta  = std::min(beta,  tt_cell->score);
        }
        if (alpha >= beta) {
            KillerTable::add_move(tt_cell->move, depth);
            leaves++;
            return { tt_cell->move, tt_cell->score };
        }
    }
    Move priority_move = tt_hit ? tt_cell->move : Move();

    // Null-Move Heuristic

    U64 checks = b.get_checks<Color>();
    I16 static_eval = (turn ? 1 : -1) * Evaluate::pestos(b);

    bool has_piece_req = (
        b.get_bitboard(Color::ALL) != (
            b.get_bitboard(Color::PAWN)
            | b.get_bitboard(Color::KING)
        )
    );
    bool has_static_req = static_eval > beta;
    bool has_depth_req = depth >= NULL_DEPTH_REDUCTION;
    bool try_null_move = has_depth_req & has_piece_req & (checks == 0ULL) & has_static_req;

    if (try_null_move) {
        Context new_ctx = ctx;
        new_ctx.toggle_hash_turn();
        new_ctx.en_passant = 0;
        MoveScore null_best = (
            turn ? nega_max<Black>(b, new_ctx, depth - NULL_DEPTH_REDUCTION, -beta, -alpha)
                 : nega_max<White>(b, new_ctx, depth - NULL_DEPTH_REDUCTION, -beta, -alpha)
        );
        null_best.score *= -1;
        new_ctx.toggle_hash_turn();

        if (null_best.score >= beta) {
            leaves++;
            return { Move(), beta }; // eval after not moving
        }
    }

    // Tests Moves.

    MoveList ml;
    b.gen_order_moves<Color, Gen::PSEUDOS>(ml, ctx, priority_move, depth);

    MoveScore best = { Move(), -INFINITY };
    I16 legal_move_count = 0;

    for (int i = 0; i < ml.size(); i++) {
        // do move
        Move& move = ml[i];
        Context new_ctx = b.do_move<Color>(move, ctx);

        // filter out illegal moves
        if (b.get_checks<Color>()) { 
            b.undo_move<Color>(move);
            continue;
        }
        legal_move_count++;

        // determine local evaluation
        MoveScore local_best = (
            turn ? nega_max<Black>(b, new_ctx, depth - 1, -beta, -alpha)
                 : nega_max<White>(b, new_ctx, depth - 1, -beta, -alpha)
        );
        local_best.score *= -1;

        // undo move
        b.undo_move<Color>(move);

        // use new evaluation
        if ((best.move.get_raw() == 0) | (local_best.score > best.score)) {
            best = { move, local_best.score };
        }
        alpha = std::max(alpha, best.score);
        if (alpha >= beta) {
            KillerTable::add_move(move, depth);
            break;
        }
    }

    // Checkmate or Stalemate.

    if (legal_move_count == 0) {
        leaves++;
        I16 score = b.get_checks<Color>() ? -INFINITY : 0;
        return { Move(), score };
    }

    // TT-update: with new result.

    TranspositionTable::set_cell(
        tt_cell, ctx.hash, depth,
        best, og_alpha, beta
    );

    return best;
}

template<class Color>
MoveScore Search::nega_scout(
    Board& b,
    Context& ctx,
    U16 depth,
    I16 alpha,
    I16 beta
) {
    constexpr bool turn = std::is_same<Color, White>::value;
    I16 og_alpha = alpha;
    I16 og_beta = beta;
    nodes++;

    if (DrawTable::is_draw()) {
        leaves++;
        return { Move(), 0 };
    }

    // TT-lookup: to adjust bounds and get priority move.

    auto [ tt_hit, tt_cell ] = TranspositionTable::get_cell(ctx.hash, depth);
    if (tt_hit && tt_cell->get_depth() >= depth) {
        switch (tt_cell->node_type) {
            case TranspositionTable::NodeType::EXACT: return { tt_cell->move, tt_cell->score };
            case TranspositionTable::NodeType::LOWER: alpha = std::max(alpha, tt_cell->score);
            case TranspositionTable::NodeType::UPPER: beta  = std::min(beta,  tt_cell->score);
        }
        if (alpha >= beta) return { tt_cell->move, tt_cell->score };
    }
    Move priority_move = tt_hit ? tt_cell->move : Move();

    // Quiescence: at nega_max leaf.

    if (depth == 0) {
        leaves++;
        I16 score = quiesce<Color>(b, ctx, alpha, beta);
        return { Move(), score };
    }

    // Tests Moves.

    MoveList ml;
    b.gen_moves<Color, Gen::PSEUDOS>(ml, ctx, priority_move, depth);

    MoveScore best = { Move(), -INFINITY };
    I16 legal_move_count = 0;

    for (int i = 0; i < ml.size(); i++) {
        // do move
        Move& move = ml[i];
        Context new_ctx = b.do_move<Color>(move, ctx);

        // filter out illegal moves
        if (b.get_checks<Color>()) { 
            b.undo_move<Color>(move);
            continue;
        }
        legal_move_count++;

        // determine local evaluation
        MoveScore local_best = (
            turn ? nega_scout<Black>(b, new_ctx, depth - 1, -beta, -og_alpha)
                 : nega_scout<White>(b, new_ctx, depth - 1, -beta, -og_alpha)
        );
        local_best.score *= -1;

        if ((local_best.score > alpha) & (local_best.score < og_beta) & (i > 0)) {
            MoveScore local_best = (
                turn ? nega_scout<Black>(b, new_ctx, depth - 1, -og_beta, -og_alpha)
                    : nega_scout<White>(b, new_ctx, depth - 1, -og_beta, -og_alpha)
            );
            local_best.score *= -1;
        }

        // undo move
        b.undo_move<Color>(move);

        // use new evaluation
        if ((best.move.get_raw() == 0) | (local_best.score > best.score)) {
            best = { move, local_best.score };
        }
        og_alpha = std::max(og_alpha, best.score);
        if (og_alpha >= og_beta) break;
        beta = og_alpha + 1;
    }

    // Checkmate or Stalemate.

    if (legal_move_count == 0) {
        leaves++;
        I16 score = b.get_checks<Color>() ? -INFINITY : 0;
        best = { Move(), score };
    }

    // TT-update: with new result.

    TranspositionTable::set_cell(
        tt_cell, ctx.hash, depth,
        best, og_alpha, beta
    );

    return best;
}